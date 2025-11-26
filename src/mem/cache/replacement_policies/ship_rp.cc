/**
 * Copyright (c) 2019, 2020 Inria
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "mem/cache/replacement_policies/ship_rp.hh"

#include "base/logging.hh"
#include "params/SHiPMemRP.hh"
#include "params/SHiPPCRP.hh"
#include "params/SHiPRP.hh"

namespace gem5
{

namespace replacement_policy
{

SHiPRP::SHiPRPReplData::SHiPRPReplData(int num_bits)
  : BRRIPRPReplData(num_bits), signature(0), outcome(false)
{
}

SHiPRP::SignatureType
SHiPRP::SHiPRPReplData::getSignature() const
{
    return signature;
}

void
SHiPRP::SHiPRPReplData::setSignature(SignatureType new_signature)
{
    signature = new_signature;
    outcome = false;
}

void
SHiPRP::SHiPRPReplData::setReReferenced()
{
    outcome = true;
}

bool
SHiPRP::SHiPRPReplData::wasReReferenced() const
{
    return outcome;
}

SHiPRP::SHiPRP(const Params &p)
  : BRRIPRP(p), insertionThreshold(p.insertion_threshold / 100.0),
    SHCT(p.shct_size, SatCounter8(numRRPVBits))
{
}

void
SHiPRP::invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
{
    std::shared_ptr<SHiPRPReplData> casted_replacement_data =
        std::static_pointer_cast<SHiPRPReplData>(replacement_data);

    // The predictor is detrained when an entry that has not been re-
    // referenced since insertion is invalidated
    if (casted_replacement_data->wasReReferenced()) {
        SHCT[casted_replacement_data->getSignature()]--;
    }

    BRRIPRP::invalidate(replacement_data);
}

void
SHiPRP::touch(const std::shared_ptr<ReplacementData>& replacement_data,
    const PacketPtr pkt)
{
    std::shared_ptr<SHiPRPReplData> casted_replacement_data =
        std::static_pointer_cast<SHiPRPReplData>(replacement_data);

    // When a hit happens the SHCT entry indexed by the signature is
    // incremented
    SHCT[getSignature(pkt)]++;
    casted_replacement_data->setReReferenced();

    // This was a hit; update replacement data accordingly
    BRRIPRP::touch(replacement_data);
}

void
SHiPRP::touch(const std::shared_ptr<ReplacementData>& replacement_data)
    const
{
    panic("Cant train SHiP's predictor without access information.");
}

void
SHiPRP::reset(const std::shared_ptr<ReplacementData>& replacement_data,
    const PacketPtr pkt)
{
    std::shared_ptr<SHiPRPReplData> casted_replacement_data =
        std::static_pointer_cast<SHiPRPReplData>(replacement_data);

    // Get signature
    const SignatureType signature = getSignature(pkt);

    // Store signature
    casted_replacement_data->setSignature(signature);

    // If SHCT for signature is set, predict intermediate re-reference.
    // Predict distant re-reference otherwise
    BRRIPRP::reset(replacement_data);
    if (SHCT[signature].calcSaturation() >= insertionThreshold) {
        casted_replacement_data->rrpv--;
    }
}

void
SHiPRP::reset(const std::shared_ptr<ReplacementData>& replacement_data)
    const
{
    panic("Cant train SHiP's predictor without access information.");
}

std::shared_ptr<ReplacementData>
SHiPRP::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new SHiPRPReplData(numRRPVBits));
}

SHiPMemRP::SHiPMemRP(const SHiPMemRPParams &p) : SHiPRP(p) {}

SHiPRP::SignatureType
SHiPMemRP::getSignature(const PacketPtr pkt) const
{
    return static_cast<SignatureType>(pkt->getAddr() % SHCT.size());
}

SHiPPCRP::SHiPPCRP(const SHiPPCRPParams &p) : SHiPRP(p) {}

SHiPRP::SignatureType
SHiPPCRP::getSignature(const PacketPtr pkt) const
{
    SignatureType signature;

    if (pkt->req->hasPC()) {
        signature = static_cast<SignatureType>(pkt->req->getPC());
    } else {
        signature = NO_PC_SIGNATURE;
    }

    return signature % SHCT.size();
}

} // namespace replacement_policy
} // namespace gem5
