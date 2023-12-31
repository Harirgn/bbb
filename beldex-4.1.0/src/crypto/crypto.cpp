// Copyright (c) 2014-2019, The Monero Project
// 
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Parts of this file are originally copyright (c) 2012-2013 The Cryptonote developers

#include <mutex>
#include <unistd.h>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <memory>
#include <stdexcept>

#include "common/varint.h"
#include "epee/warnings.h"
#include "crypto.h"
extern "C" {
#include "keccak.h"
}
#include "hash.h"

namespace {
  void local_abort(const char *msg)
  {
    fprintf(stderr, "%s\n", msg);
#ifdef NDEBUG
    _exit(1);
#else
    abort();
#endif
  }
}

namespace crypto {

  using std::abort;
  using std::int32_t;
  using std::int64_t;
  using std::size_t;
  using std::uint32_t;
  using std::uint64_t;

  extern "C" {
#include "crypto-ops.h"
#include "random.h"
  }

  // These nasty dirty hacks are unspeakable disgusting.  This is only here because all of these
  // have a `.data` element, but it is a `char` instead of an `unsigned char`.  So rather than
  // change it to `unsigned char`, the author decided that he should overload `&` to do a
  // reinterpret_cast.  WTF.
  //
  // TODO: fix this garbage by making the ec_ types use unsigned char instead of char.

  // EW!
  static inline unsigned char *operator &(ec_point &point) {
    return &reinterpret_cast<unsigned char &>(point);
  }

  // EW!
  static inline const unsigned char *operator &(const ec_point &point) {
    return &reinterpret_cast<const unsigned char &>(point);
  }

  // EW!
  static inline unsigned char *operator &(ec_scalar &scalar) {
    return &reinterpret_cast<unsigned char &>(scalar);
  }

  // EW!
  static inline const unsigned char *operator &(const ec_scalar &scalar) {
    return &reinterpret_cast<const unsigned char &>(scalar);
  }

  static std::mutex random_mutex;


  void generate_random_bytes_thread_safe(size_t N, uint8_t *bytes)
  {
    std::lock_guard lock{random_mutex};
    generate_random_bytes_not_thread_safe(N, bytes);
  }

  void add_extra_entropy_thread_safe(const void *ptr, size_t bytes)
  {
    std::lock_guard lock{random_mutex};
    add_extra_entropy_not_thread_safe(ptr, bytes);
  }

  // 2^252+27742317777372353535851937790883648493
  static constexpr unsigned char L[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
  };

  // Returns true iff 32-byte, little-endian unsigned integer a is less than L
  static inline bool sc_is_canonical(const unsigned char* a)
  {
    for (size_t n = 31; n < 32; --n)
    {
      if (a[n] < L[n])
        return true;
      if (a[n] > L[n])
        return false;
    }
    return false;
  }

  void random_scalar(unsigned char *bytes)
  {
    std::lock_guard lock{random_mutex};
    do
    {
      generate_random_bytes_not_thread_safe(32, bytes);
      bytes[31] &= 0b0001'1111; // Mask the 3 most significant bits off because no acceptable value ever has them set (the value would be >L)
    } while (!(sc_is_canonical(bytes) && sc_isnonzero(bytes)));
  }
  /* generate a random ]0..L[ scalar */
  void random_scalar(ec_scalar &res) {
    random_scalar(reinterpret_cast<unsigned char*>(res.data));
  }

  ec_scalar random_scalar() {
    ec_scalar res;
    random_scalar(res);
    return res;
  }

  void hash_to_scalar(const void *data, size_t length, ec_scalar &res) {
    cn_fast_hash(data, length, reinterpret_cast<hash &>(res));
    sc_reduce32(&res);
  }

  /*
   * generate public and secret keys from a random 256-bit integer
   */
  secret_key generate_keys(public_key &pub, secret_key &sec, const secret_key& recovery_key, bool recover) {
    ge_p3 point;

    secret_key rng;

    if (recover)
    {
      rng = recovery_key;
    }
    else
    {
      random_scalar(rng);
    }
    sec = rng;
    sc_reduce32(&unwrap(sec));  // reduce in case second round of keys (sendkeys)

    ge_scalarmult_base(&point, &unwrap(sec));
    ge_p3_tobytes(&pub, &point);

    return rng;
  }

  bool check_key(const public_key &key) {
    ge_p3 point;
    return ge_frombytes_vartime(&point, &key) == 0;
  }

  bool secret_key_to_public_key(const secret_key &sec, public_key &pub) {
    ge_p3 point;
    if (sc_check(&unwrap(sec)) != 0) {
      return false;
    }
    ge_scalarmult_base(&point, &unwrap(sec));
    ge_p3_tobytes(&pub, &point);
    return true;
  }

  bool generate_key_derivation(const public_key &key1, const secret_key &key2, key_derivation &derivation) {
    ge_p3 point;
    ge_p2 point2;
    ge_p1p1 point3;
    assert(sc_check(&key2) == 0);
    if (ge_frombytes_vartime(&point, &key1) != 0) {
      return false;
    }
    ge_scalarmult(&point2, &unwrap(key2), &point);
    ge_mul8(&point3, &point2);
    ge_p1p1_to_p2(&point2, &point3);
    ge_tobytes(&derivation, &point2);
    return true;
  }

  void derivation_to_scalar(const key_derivation &derivation, size_t output_index, ec_scalar &res) {
    struct {
      key_derivation derivation;
      char output_index[tools::VARINT_MAX_LENGTH<size_t>];
    } buf;
    char *end = buf.output_index;
    buf.derivation = derivation;
    tools::write_varint(end, output_index);
    hash_to_scalar(&buf, end - reinterpret_cast<char *>(&buf), res);
  }

  bool derive_public_key(const key_derivation &derivation, size_t output_index,
    const public_key &base, public_key &derived_key) {
    ec_scalar scalar;
    ge_p3 point1;
    ge_p3 point2;
    ge_cached point3;
    ge_p1p1 point4;
    ge_p2 point5;
    if (ge_frombytes_vartime(&point1, &base) != 0) {
      return false;
    }
    derivation_to_scalar(derivation, output_index, scalar);
    ge_scalarmult_base(&point2, &scalar);
    ge_p3_to_cached(&point3, &point2);
    ge_add(&point4, &point1, &point3);
    ge_p1p1_to_p2(&point5, &point4);
    ge_tobytes(&derived_key, &point5);
    return true;
  }

  void derive_secret_key(const key_derivation &derivation, size_t output_index,
    const secret_key &base, secret_key &derived_key) {
    ec_scalar scalar;
    assert(sc_check(&base) == 0);
    derivation_to_scalar(derivation, output_index, scalar);
    sc_add(&unwrap(derived_key), &unwrap(base), &scalar);
  }

  bool derive_subaddress_public_key(const public_key &out_key, const key_derivation &derivation, std::size_t output_index, public_key &derived_key) {
    ec_scalar scalar;
    ge_p3 point1;
    ge_p3 point2;
    ge_cached point3;
    ge_p1p1 point4;
    ge_p2 point5;
    if (ge_frombytes_vartime(&point1, &out_key) != 0) {
      return false;
    }
    derivation_to_scalar(derivation, output_index, scalar);
    ge_scalarmult_base(&point2, &scalar);
    ge_p3_to_cached(&point3, &point2);
    ge_sub(&point4, &point1, &point3);
    ge_p1p1_to_p2(&point5, &point4);
    ge_tobytes(&derived_key, &point5);
    return true;
  }

  struct s_comm {
    hash h;
    ec_point key;
    ec_point comm;
  };

  struct s_comm_2 {
    hash msg;
    ec_point D;
    ec_point X;
    ec_point Y;
  };

  void generate_signature(const hash &prefix_hash, const public_key &pub, const secret_key &sec, signature &sig) {
    ge_p3 tmp3;
    ec_scalar k;
    s_comm buf;
#if !defined(NDEBUG)
    {
      ge_p3 t;
      public_key t2;
      assert(sc_check(&sec) == 0);
      ge_scalarmult_base(&t, &sec);
      ge_p3_tobytes(&t2, &t);
      assert(pub == t2);
    }
#endif
    buf.h = prefix_hash;
    buf.key = pub;
  try_again:
    random_scalar(k);
    ge_scalarmult_base(&tmp3, &k);
    ge_p3_tobytes(&buf.comm, &tmp3);
    hash_to_scalar(&buf, sizeof(s_comm), sig.c);
    if (!sc_isnonzero((const unsigned char*)sig.c.data))
      goto try_again;
    sc_mulsub(&sig.r, &sig.c, &unwrap(sec), &k);
    if (!sc_isnonzero((const unsigned char*)sig.r.data))
      goto try_again;
    memwipe(&k, sizeof(k));
  }

  static constexpr ec_point infinity = {{ 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

  bool check_signature(const hash &prefix_hash, const public_key &pub, const signature &sig) {
    ge_p2 tmp2;
    ge_p3 tmp3;
    ec_scalar c;
    s_comm buf;
    assert(check_key(pub));
    buf.h = prefix_hash;
    buf.key = pub;
    if (ge_frombytes_vartime(&tmp3, &pub) != 0) {
      return false;
    }
    if (sc_check(&sig.c) != 0 || sc_check(&sig.r) != 0 || !sc_isnonzero(&sig.c)) {
      return false;
    }
    ge_double_scalarmult_base_vartime(&tmp2, &sig.c, &tmp3, &sig.r); // tmp2 = sig.c A + sig.r G
    ge_tobytes(&buf.comm, &tmp2);
    if (memcmp(&buf.comm, &infinity, 32) == 0)
      return false;
    hash_to_scalar(&buf, sizeof(s_comm), c);
    sc_sub(&c, &c, &sig.c);
    return sc_isnonzero(&c) == 0;
  }

  void generate_tx_proof(const hash &prefix_hash, const public_key &R, const public_key &A, const std::optional<public_key> &B, const public_key &D, const secret_key &r, signature &sig) {
    // sanity check
    ge_p3 R_p3;
    ge_p3 A_p3;
    ge_p3 B_p3;
    ge_p3 D_p3;
    if (ge_frombytes_vartime(&R_p3, &R) != 0) throw std::runtime_error("tx pubkey is invalid");
    if (ge_frombytes_vartime(&A_p3, &A) != 0) throw std::runtime_error("recipient view pubkey is invalid");
    if (B && ge_frombytes_vartime(&B_p3, &*B) != 0) throw std::runtime_error("recipient spend pubkey is invalid");
    if (ge_frombytes_vartime(&D_p3, &D) != 0) throw std::runtime_error("key derivation is invalid");
#if !defined(NDEBUG)
    {
      assert(sc_check(&r) == 0);
      // check R == r*G or R == r*B
      public_key dbg_R;
      if (B)
      {
        ge_p2 dbg_R_p2;
        ge_scalarmult(&dbg_R_p2, &r, &B_p3);
        ge_tobytes(&dbg_R, &dbg_R_p2);
      }
      else
      {
        ge_p3 dbg_R_p3;
        ge_scalarmult_base(&dbg_R_p3, &r);
        ge_p3_tobytes(&dbg_R, &dbg_R_p3);
      }
      assert(R == dbg_R);
      // check D == r*A
      ge_p2 dbg_D_p2;
      ge_scalarmult(&dbg_D_p2, &r, &A_p3);
      public_key dbg_D;
      ge_tobytes(&dbg_D, &dbg_D_p2);
      assert(D == dbg_D);
    }
#endif

    // pick random k
    ec_scalar k = random_scalar();
    s_comm_2 buf;
    buf.msg = prefix_hash;
    buf.D = D;

    if (B)
    {
      // compute X = k*B
      ge_p2 X_p2;
      ge_scalarmult(&X_p2, &k, &B_p3);
      ge_tobytes(&buf.X, &X_p2);
    }
    else
    {
      // compute X = k*G
      ge_p3 X_p3;
      ge_scalarmult_base(&X_p3, &k);
      ge_p3_tobytes(&buf.X, &X_p3);
    }

    // compute Y = k*A
    ge_p2 Y_p2;
    ge_scalarmult(&Y_p2, &k, &A_p3);
    ge_tobytes(&buf.Y, &Y_p2);

    // sig.c = Hs(Msg || D || X || Y)
    hash_to_scalar(&buf, sizeof(buf), sig.c);

    // sig.r = k - sig.c*r
    sc_mulsub(&sig.r, &sig.c, &unwrap(r), &k);

    memwipe(&k, sizeof(k));
  }

  bool check_tx_proof(const hash &prefix_hash, const public_key &R, const public_key &A, const std::optional<public_key> &B, const public_key &D, const signature &sig) {
    // sanity check
    ge_p3 R_p3;
    ge_p3 A_p3;
    ge_p3 B_p3;
    ge_p3 D_p3;
    if (ge_frombytes_vartime(&R_p3, &R) != 0) return false;
    if (ge_frombytes_vartime(&A_p3, &A) != 0) return false;
    if (B && ge_frombytes_vartime(&B_p3, &*B) != 0) return false;
    if (ge_frombytes_vartime(&D_p3, &D) != 0) return false;
    if (sc_check(&sig.c) != 0 || sc_check(&sig.r) != 0) return false;

    // compute sig.c*R
    ge_p3 cR_p3;
    {
      ge_p2 cR_p2;
      ge_scalarmult(&cR_p2, &sig.c, &R_p3);
      public_key cR;
      ge_tobytes(&cR, &cR_p2);
      if (ge_frombytes_vartime(&cR_p3, &cR) != 0) return false;
    }

    ge_p1p1 X_p1p1;
    if (B)
    {
      // compute X = sig.c*R + sig.r*B
      ge_p2 rB_p2;
      ge_scalarmult(&rB_p2, &sig.r, &B_p3);
      public_key rB;
      ge_tobytes(&rB, &rB_p2);
      ge_p3 rB_p3;
      if (ge_frombytes_vartime(&rB_p3, &rB) != 0) return false;
      ge_cached rB_cached;
      ge_p3_to_cached(&rB_cached, &rB_p3);
      ge_add(&X_p1p1, &cR_p3, &rB_cached);
    }
    else
    {
      // compute X = sig.c*R + sig.r*G
      ge_p3 rG_p3;
      ge_scalarmult_base(&rG_p3, &sig.r);
      ge_cached rG_cached;
      ge_p3_to_cached(&rG_cached, &rG_p3);
      ge_add(&X_p1p1, &cR_p3, &rG_cached);
    }
    ge_p2 X_p2;
    ge_p1p1_to_p2(&X_p2, &X_p1p1);

    // compute sig.c*D
    ge_p2 cD_p2;
    ge_scalarmult(&cD_p2, &sig.c, &D_p3);

    // compute sig.r*A
    ge_p2 rA_p2;
    ge_scalarmult(&rA_p2, &sig.r, &A_p3);

    // compute Y = sig.c*D + sig.r*A
    public_key cD;
    public_key rA;
    ge_tobytes(&cD, &cD_p2);
    ge_tobytes(&rA, &rA_p2);
    ge_p3 cD_p3;
    ge_p3 rA_p3;
    if (ge_frombytes_vartime(&cD_p3, &cD) != 0) return false;
    if (ge_frombytes_vartime(&rA_p3, &rA) != 0) return false;
    ge_cached rA_cached;
    ge_p3_to_cached(&rA_cached, &rA_p3);
    ge_p1p1 Y_p1p1;
    ge_add(&Y_p1p1, &cD_p3, &rA_cached);
    ge_p2 Y_p2;
    ge_p1p1_to_p2(&Y_p2, &Y_p1p1);

    // compute c2 = Hs(Msg || D || X || Y)
    s_comm_2 buf;
    buf.msg = prefix_hash;
    buf.D = D;
    ge_tobytes(&buf.X, &X_p2);
    ge_tobytes(&buf.Y, &Y_p2);
    ec_scalar c2;
    hash_to_scalar(&buf, sizeof(s_comm_2), c2);

    // test if c2 == sig.c
    sc_sub(&c2, &c2, &sig.c);
    return sc_isnonzero(&c2) == 0;
  }

  static void hash_to_ec(const public_key &key, ge_p3 &res) {
    hash h;
    ge_p2 point;
    ge_p1p1 point2;
    cn_fast_hash(&key, sizeof(public_key), h);
    ge_fromfe_frombytes_vartime(&point, reinterpret_cast<const unsigned char *>(&h));
    ge_mul8(&point2, &point);
    ge_p1p1_to_p3(&res, &point2);
  }

  void generate_key_image(const public_key &pub, const secret_key &sec, key_image &image) {
    ge_p3 point;
    ge_p2 point2;
    assert(sc_check(&sec) == 0);
    hash_to_ec(pub, point);
    ge_scalarmult(&point2, &unwrap(sec), &point);
    ge_tobytes(&image, &point2);
  }

  struct rs_comm {
    hash prefix;
    std::vector<std::pair<ec_point, ec_point>> ab;

    rs_comm(const hash& h, size_t pubs_count) : prefix{h}, ab{pubs_count} {}

    ec_scalar hash_to_scalar() const {
      KECCAK_CTX state;
      keccak_init(&state);
      keccak_update(&state, reinterpret_cast<const uint8_t*>(&prefix), sizeof(prefix));
      static_assert(sizeof(ab[0]) == 64); // Ensure no padding
      keccak_update(&state, reinterpret_cast<const uint8_t*>(ab.data()), 64*ab.size());
      ec_scalar result;
      keccak_finish(&state, reinterpret_cast<uint8_t*>(&result));
      sc_reduce32(&result);
      return result;
    };
  };

  void generate_ring_signature(
      const hash& prefix_hash,
      const key_image& image,
      const std::vector<const public_key*>& pubs,
      const secret_key& sec,
      size_t sec_index,
      signature* sig) {

    assert(sec_index < pubs.size());

#if !defined(NDEBUG)
    {
      ge_p3 t;
      public_key t2;
      key_image t3;
      assert(sc_check(&sec) == 0);
      ge_scalarmult_base(&t, &sec);
      ge_p3_tobytes(&t2, &t);
      assert(*pubs[sec_index] == t2);
      generate_key_image(*pubs[sec_index], sec, t3);
      assert(image == t3);
      for (size_t i = 0; i < pubs.size(); i++) {
        assert(check_key(*pubs[i]));
      }
    }
#endif
    ge_p3 image_unp; // I
    if (ge_frombytes_vartime(&image_unp, &image) != 0) {
      local_abort("invalid key image");
    }
    ge_dsmp image_pre;
    ge_dsm_precomp(image_pre, &image_unp);
    ec_scalar sum;
    sc_0(&sum); // will be sum of cj, j≠s

    rs_comm rs{prefix_hash, pubs.size()};
    ec_scalar qs;
    for (size_t i = 0; i < pubs.size(); i++) {
      ge_p2 tmp2;
      ge_p3 tmp3;
      if (i == sec_index) { // this is the true key image
        random_scalar(qs); // qs = random
        ge_scalarmult_base(&tmp3, &qs); // Ls = qs G
        ge_p3_tobytes(&rs.ab[i].first, &tmp3);
        hash_to_ec(*pubs[i], tmp3); // Hp(Ps)
        ge_scalarmult(&tmp2, &qs, &tmp3); // Rs = qs Hp(Ps)
        ge_tobytes(&rs.ab[i].second, &tmp2);
        // We don't set ci, ri yet because we first need the sum of all the other cj's/rj's
      } else {
        random_scalar(sig[i].c); // ci = wi = random
        random_scalar(sig[i].r); // ri = qi = random
        if (ge_frombytes_vartime(&tmp3, &*pubs[i]) != 0) {
          memwipe(&qs, sizeof(qs));
          local_abort("invalid pubkey");
        }
        ge_double_scalarmult_base_vartime(&tmp2, &sig[i].c, &tmp3, &sig[i].r); // Li = cj Pj + rj G = qj G + wj Pj
        ge_tobytes(&rs.ab[i].first, &tmp2);
        hash_to_ec(*pubs[i], tmp3); // Hp(Pj)
        ge_double_scalarmult_precomp_vartime(&tmp2, &sig[i].r, &tmp3, &sig[i].c, image_pre); // Ri = qj Hp(Pj) + wj I
        ge_tobytes(&rs.ab[i].second, &tmp2);
        sc_add(&sum, &sum, &sig[i].c);
      }
    }
    ec_scalar c = rs.hash_to_scalar(); // c = Hs(prefix_hash || L0 || ... || L{n-1} || R0 || ... || R{n-1})
    sc_sub(&sig[sec_index].c, &c, &sum); // cs = c - sum(ci, i≠s) = c - sum(wi)
    sc_mulsub(&sig[sec_index].r, &sig[sec_index].c, &unwrap(sec), &qs); // rs = qs - cs*x

    memwipe(&qs, sizeof(qs));
  }

  bool check_ring_signature(
      const hash& prefix_hash,
      const key_image& image,
      const std::vector<const public_key*>& pubs,
      const signature* sig) {
#if !defined(NDEBUG)
    for (size_t i = 0; i < pubs.size(); i++) {
      assert(check_key(*pubs[i]));
    }
#endif
    ge_p3 image_unp;
    if (ge_frombytes_vartime(&image_unp, &image) != 0) {
      return false;
    }
    ge_dsmp image_pre;
    ge_dsm_precomp(image_pre, &image_unp);
    ec_scalar sum;
    sc_0(&sum);

    rs_comm rs{prefix_hash, pubs.size()};
    for (size_t i = 0; i < pubs.size(); i++) {
      ge_p2 tmp2;
      ge_p3 tmp3;
      if (sc_check(&sig[i].c) != 0 || sc_check(&sig[i].r) != 0) {
        return false;
      }
      if (ge_frombytes_vartime(&tmp3, &*pubs[i]) != 0) {
        return false;
      }
      ge_double_scalarmult_base_vartime(&tmp2, &sig[i].c, &tmp3, &sig[i].r);
      ge_tobytes(&rs.ab[i].first, &tmp2);
      hash_to_ec(*pubs[i], tmp3);
      ge_double_scalarmult_precomp_vartime(&tmp2, &sig[i].r, &tmp3, &sig[i].c, image_pre);
      ge_tobytes(&rs.ab[i].second, &tmp2);
      sc_add(&sum, &sum, &sig[i].c);
    }
    ec_scalar h = rs.hash_to_scalar();
    sc_sub(&h, &h, &sum);
    return sc_isnonzero(&h) == 0;
  }

  void generate_key_image_signature(
      const key_image& image, // I
      const public_key& pub, // A
      const secret_key& sec, // a
      signature& sig) {
    static_assert(sizeof(hash) == sizeof(key_image));
    ec_scalar k = random_scalar(); // k = random
    rs_comm rs{reinterpret_cast<const hash&>(image), 1};

    ge_p3 tmp3;
    ge_scalarmult_base(&tmp3, &k); // L = kG
    ge_p3_tobytes(&rs.ab[0].first, &tmp3); // store L

    hash_to_ec(pub, tmp3); // H(A)
    ge_p2 tmp2;
    ge_scalarmult(&tmp2, &k, &tmp3); // R = kH(A)
    ge_tobytes(&rs.ab[0].second, &tmp2); // store R


    sig.c = rs.hash_to_scalar(); // c = H(I || L || R) = H(I || kG || kH(A))
    sc_mulsub(&sig.r, &sig.c, &unwrap(sec), &k); // r = k - ac = k - aH(I || kG || kH(A))

    memwipe(&k, sizeof(k));
  }

  bool check_key_image_signature(
      const key_image& image,
      const public_key& pub,
      const signature& sig) {

    assert(check_key(pub));
    ge_p3 image_unp;
    if (ge_frombytes_vartime(&image_unp, &image) != 0 || sc_check(&sig.c) != 0 || sc_check(&sig.r) != 0)
      return false;
    ge_dsmp image_pre;
    ge_dsm_precomp(image_pre, &image_unp);

    rs_comm rs{reinterpret_cast<const hash&>(image), 1};
    ge_p3 tmp3;
    if (ge_frombytes_vartime(&tmp3, &pub) != 0)
      return false;

    ge_p2 tmp2;
    // Step one: reconstruct the signer's L = kG.
    // The signature r was constructed as r = k - ac, so:
    // k  = ac + r
    // kG = cA + rG = L
    ge_double_scalarmult_base_vartime(&tmp2, &sig.c, &tmp3, &sig.r); // L = cA + rG
    ge_tobytes(&rs.ab[0].first, &tmp2); // store L

    // Step two: reconstruct the signer's R = kH(A)
    // The signature r was constructed as r = k - ac, so:
    // rH(A) = kH(A) - acH(A)
    // and since aH(A) == I (the key image, by definition):
    // kH(A) = rH(A) + cI = R
    hash_to_ec(pub, tmp3); // H(A)
    ge_double_scalarmult_precomp_vartime(&tmp2, &sig.r, &tmp3, &sig.c, image_pre); // R = rH(A) + cI
    ge_tobytes(&rs.ab[0].second, &tmp2); // store R

    // Now we can calculate our own H(I || L || R), and compare it to the signature's c (which was
    // set to the signer's H(I || L || R) calculation).
    ec_scalar h = rs.hash_to_scalar();
    sc_sub(&h, &h, &sig.c);
    return sc_isnonzero(&h) == 0;
  }

}
