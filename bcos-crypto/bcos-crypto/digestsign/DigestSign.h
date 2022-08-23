#pragma once
#include <concepts>
#include <span>
#include <type_traits>

namespace bcos::crypto::digestsign
{

template <class DigestSignType>
concept DigestSign = requires(DigestSignType digestSign, typename DigestSignType::KeyGenerator keygen,
    typename DigestSignType::Encrypter encrypter, typename DigestSignType::Signer signer,
    typename DigestSignType::Verifier verifier, typename DigestSignType::Recoverer recoverer)
{
    typename DigestSignType::Key;
    typename DigestSignType::Sign;

    typename DigestSignType::KeyGenerator;
    keygen.gen(std::as_const(typename DigestSignType::Key{}));

    typename DigestSignType::Encrypter;
    encrypter.encrypt();

    typename DigestSignType::Signer;
    typename DigestSignType::Signer{std::as_const(typename DigestSignType::Key{})};
    signer.sign(std::span<std::byte const>{}, typename DigestSignType::Sign{});

    typename DigestSignType::Verifier;
    typename DigestSignType::Verifier{std::as_const(typename DigestSignType::Key{})};
    {
        verifier.verify(std::span<std::byte const>{}, std::as_const(typename DigestSignType::Sign{}))
        } -> std::convertible_to<bool>;

    typename DigestSignType::Recoverer;
    typename DigestSignType::Recoverer{};
    recoverer.recover(std::as_const(typename DigestSignType::Sign{}), typename DigestSignType::Key{});
};

}  // namespace bcos::crypto::digestsign