# no-port-check: NereusSDR-original.
"""The crypto and derivation vectors, against the service's identity and
TURN code, and TURN passwords recomputed here with hmac directly."""

import base64
import hashlib
import hmac

import pytest
from cryptography.hazmat.primitives import serialization

from nereus_rendezvous import identity, turn
from runner import load_fixture, load_manifest

CRYPTO = {f["id"]: f["file"] for f in load_manifest()["fixtures"] if f["kind"] == "crypto"}


def test_every_crypto_file_is_listed():
    assert set(CRYPTO) == {
        "base64url",
        "p256-spki",
        "rendezvous-id",
        "register-proof",
        "introduce-signature",
        "turn-credentials",
    }


def test_base64url():
    for case in load_fixture(CRYPTO["base64url"])["cases"]:
        decoded = identity.from_b64url(case["text"])
        if case["valid"]:
            assert decoded is not None and decoded.hex() == case["bytesHex"], case
        else:
            assert decoded is None, case


def test_p256_spki():
    for case in load_fixture(CRYPTO["p256-spki"])["cases"]:
        assert identity.is_p256_spki(bytes.fromhex(case["spkiHex"])) is case["valid"], case["name"]


def test_rendezvous_id():
    vectors = load_fixture(CRYPTO["rendezvous-id"])
    assert bytes.fromhex(vectors["prefixHex"]) == b"NereusSDR rendezvous id v1\n"
    for case in vectors["cases"]:
        spki = identity.from_b64url(case["publicKey"])
        assert hashlib.sha256(identity.ID_PREFIX + spki).hexdigest() == case["digestHex"]
        assert identity.rendezvous_id(spki) == case["id"]
        # The id is the first 26 characters of the lowercased base32 of the
        # digest, computed here without the service's code.
        assert base64.b32encode(bytes.fromhex(case["digestHex"])).decode().lower()[:26] == case["id"]
        assert identity.is_rendezvous_id(case["id"])


def _test_key(block):
    assert "never be used" in block["testOnly"]
    return serialization.load_der_private_key(identity.from_b64url(block["privateKeyPkcs8"]), None)


def test_register_proof():
    vectors = load_fixture(CRYPTO["register-proof"])
    key = _test_key(vectors["key"])
    spki = identity.spki_of(key.public_key())
    assert identity.to_b64url(spki) == vectors["key"]["publicKey"]
    assert identity.rendezvous_id(spki) == vectors["key"]["id"]
    nonce = identity.from_b64url(vectors["nonce"])
    transcript = identity.register_transcript(nonce)
    assert transcript.hex() == vectors["transcriptHex"]
    assert transcript == b"NereusSDR rendezvous register v1\n" + nonce
    names = set()
    for case in vectors["cases"]:
        names.add(case["name"])
        pub = identity.b64url_of_length(case["publicKey"], identity.SPKI_BYTES)
        sig = identity.b64url_of_length(case["signature"], identity.SIGNATURE_BYTES)
        ok = pub is not None and sig is not None and identity.verify_raw(pub, transcript, sig)
        assert ok is case["valid"], case["name"]
    assert {"signed", "otherNonce", "flippedBit", "keyCompressed", "keyOtherCurve", "keyBadBase64url"} <= names
    # A fresh signature by the test key verifies too.
    assert identity.verify_raw(spki, transcript, identity.sign_raw(key, transcript))


def test_introduce_signature():
    vectors = load_fixture(CRYPTO["introduce-signature"])
    device = _test_key(vectors["device"])
    spki = identity.spki_of(device.public_key())
    assert identity.to_b64url(identity.fingerprint(spki)) == vectors["device"]["id"]
    nonce = identity.from_b64url(vectors["nonce"])
    transcript = identity.introduce_transcript(vectors["stationId"], nonce)
    assert transcript.hex() == vectors["transcriptHex"]
    assert transcript == b"NereusSDR introduce v1\n" + vectors["stationId"].encode("ascii") + nonce
    for case in vectors["cases"]:
        sig = identity.b64url_of_length(case["signature"], identity.SIGNATURE_BYTES)
        assert (sig is not None and identity.verify_raw(spki, transcript, sig)) is case["valid"], case["name"]


def test_turn_credentials():
    for case in load_fixture(CRYPTO["turn-credentials"])["cases"]:
        secret = case["secret"].encode("utf-8")
        assert turn.username_for(case["expires"], case["stationId"]) == case["username"]
        assert turn.password_for(secret, case["username"]) == case["password"]
        independent = base64.b64encode(hmac.new(secret, case["username"].encode(), hashlib.sha1).digest()).decode()
        assert independent == case["password"]
        minted = turn.mint(secret, case["stationId"], case["expires"] - 86400, 86400, ["turn:x"])
        assert minted == {"username": case["username"], "password": case["password"], "expires": case["expires"], "urls": ["turn:x"]}


@pytest.mark.parametrize("text", ["", "A" * 26, "a" * 25, "a" * 27, "abcdefghijklmnopqrstuvwxy1", "abcdefghijklmnopqrstuvwxy8"])
def test_rendezvous_id_shape_refused(text):
    assert not identity.is_rendezvous_id(text)
