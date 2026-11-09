#include "rs_t3k/Pkce.h"

#include <catch2/catch_test_macros.hpp>

using namespace rs::t3k;

TEST_CASE ("PKCE: RFC 7636's example", "[t3k]")
{
    // Appendix B
    CHECK (Pkce::challengeFor ("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk")
           == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
}

TEST_CASE ("PKCE verifiers and random tokens are URL-safe and different each time", "[t3k]")
{
    const auto a = Pkce::create(), b = Pkce::create();
    CHECK (a.verifier != b.verifier);
    CHECK (a.verifier.length() >= 43);
    CHECK (a.verifier.length() <= 128);
    CHECK (a.verifier.containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"));
    CHECK (a.challenge == Pkce::challengeFor (a.verifier));
    CHECK_FALSE (randomToken (16).containsAnyOf ("+/="));
}
