#pragma once

#ifndef UUID_HPP__
#define UUID_HPP__

#include "serializer.hpp"

#include <Poco/Crypto/DigestEngine.h>

#include <random>

namespace uuid
{

class uuid : public pack::key_t
{
public:
};

uuid get_uuid(std::string const& buffer)
{
    uuid id;
    Poco::Crypto::DigestEngine engine{"SHA256"};
    engine.update(buffer.data(), buffer.size());
    Poco::DigestEngine::Digest const& digest = engine.digest();

    std::copy(digest.begin(), digest.end(), id.begin());
    std::cout << Poco::DigestEngine::digestToHex(digest) << "\n";
    return id;
}

uuid gen_uuid()
{
    static std::mt19937 rng;
    uuid id;
    std::random_device rd;
    Poco::Crypto::DigestEngine engine{"SHA256"};

    rng.seed(rd());
    int const r1 = rng();
    engine.update(&r1, sizeof(r1));

    rng.seed(rd());
    int const r2 = rng();
    engine.update(&r2, sizeof(r2));

    Poco::DigestEngine::Digest const& digest = engine.digest();

    std::copy(digest.begin(), digest.end(), id.begin());
    std::cout << Poco::DigestEngine::digestToHex(digest) << "\n";
    return id;
}

} // namespace uuid
#endif // UUID_HPP__
