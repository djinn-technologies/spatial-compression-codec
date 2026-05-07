# Spatial Compression Codec — Source-Available Reference License v1.0

**Copyright © 2024–2026 Djinn Technologies Ltd.
(a subsidiary of Akuma Engineering Ltd.)
Company No. 13918535, registered in England and Wales.
All rights reserved.**

> **Legal-review status.** This document is the project's licensing
> framework as currently drafted. All jurisdiction, contact, and
> commercial-term values have been confirmed by Djinn Technologies
> Ltd.

---

## 0. Why this license exists

The Spatial Compression Codec ("**SCC**", "the **Software**") implements
methods disclosed in **United States Patent No. 10,827,161 B2**
("Method for Real-Time Compression of 3D Video Streaming Frames"; the
"**Patent**"), owned by Djinn Technologies Ltd. ("**Djinn**", "**we**",
"**us**"). The Patent is the primary intellectual-property asset.

We want to be open about how SCC is built, allow researchers and
engineers to read, study, run, and contribute to the code, and let
prospective customers evaluate the codec on their own data, without
giving away the underlying patent rights. This license is therefore
**source-available with an express reservation of patent rights**: the
copyright in the source code is licensed to You on permissive terms
for the uses listed below, but no patent licence is granted by this
document. Commercial deployment of SCC in any jurisdiction where the
Patent (or any equivalent or counterpart) is in force requires a
separate written patent licence from Djinn.

If you are unsure whether your intended use is permitted, please
contact us before proceeding (see §10).

---

## 1. Definitions

1.1 **"Software"** means the source code, object code, build scripts,
documentation, test fixtures, and any other materials in this
repository, together with any updates, modifications, or derivative
works supplied by Djinn.

1.2 **"Patent"** means United States Patent No. 10,827,161 B2 and any
continuations, continuations-in-part, divisions, reissues, re-
examinations, foreign equivalents, or other patents or patent
applications that claim priority from or are otherwise based on the
inventions disclosed in that patent.

1.3 **"You"** (or **"Your"**) means the individual or legal entity
exercising rights under this licence. If You are exercising rights on
behalf of Your employer or another entity, "You" means that entity.

1.4 **"Permitted Use"** means any of the following:

- **Read and study** — viewing the source for personal study, review,
  audit, or research purposes.
- **Internal evaluation** — running the Software internally within Your
  organisation for the sole purpose of evaluating its suitability for a
  potential Commercial Use, for a period not exceeding sixty (60) days
  per evaluation.
- **Non-commercial research and education** — using the Software in
  academic research, in not-for-profit teaching, or in personal hobby
  projects that are not, and are not intended to be, distributed,
  monetised, or used to deliver a commercial product or service.
- **Contribution** — modifying the Software and submitting the
  modifications back to Djinn under §4.

1.5 **"Commercial Use"** means any use of the Software, in whole or in
part, that is not a Permitted Use, including without limitation:

- distributing the Software (modified or unmodified) to third parties;
- incorporating the Software into a product or service that is sold,
  licensed, rented, or otherwise made available for consideration
  (whether monetary or in-kind);
- using the Software to provide a hosted service ("software as a
  service"), media-streaming service, or telepresence service to third
  parties;
- using the Software in any production system that processes data of
  third parties or end-users;
- using the Software in connection with the manufacture, sale, lease,
  or licensing of hardware or software products; and
- exercising any right granted under the Patent.

---

## 2. Copyright Licence

Subject to the terms of this licence, Djinn grants You a worldwide,
royalty-free, non-exclusive, non-transferable, non-sublicensable
copyright licence to reproduce, prepare derivative works of, and
internally use the Software **solely for Permitted Use**.

This licence does **not** authorise:

- Commercial Use of any kind;
- distribution of the Software, or of derivative works of the Software,
  to any third party (other than as a contribution to Djinn under §4);
  or
- removal or alteration of any copyright, patent, trademark, or
  attribution notice in the Software.

---

## 3. Patent Rights — Express Reservation

**No patent rights are granted by this licence**, whether expressly,
impliedly, by exhaustion, by estoppel, or otherwise. The Patent and
all related rights remain the exclusive property of Djinn.

In particular, but without limitation, this licence does **not** grant
You the right to:

- practise any claim of the Patent;
- import, make, use, sell, offer for sale, lease, or licence any
  product, service, or system that practises any claim of the Patent;
  or
- authorise any third party to do any of the above.

If You wish to do any of the above, You must obtain a separate written
patent licence from Djinn. See §10 for contact details.

This express reservation survives termination of this licence.

---

## 4. Contributions

If You submit a modification, fix, suggestion, or other contribution
("**Contribution**") to Djinn (whether through a pull request, issue,
email, or any other channel), You hereby:

4.1 **Copyright assignment.** Assign to Djinn, at no cost, all
copyright in and to the Contribution, with effect from the moment of
submission. Where assignment is not permitted by applicable law, You
grant Djinn an exclusive, perpetual, irrevocable, worldwide,
royalty-free, sublicensable copyright licence to use the Contribution
for any purpose.

4.2 **Patent non-assertion.** Agree that You will not assert any
patent owned or controlled by You against Djinn or any of Djinn's
licensees of the Software in respect of the Software or any
combination thereof, to the extent that such assertion would be based
on the inclusion of Your Contribution.

4.3 **Originality.** Represent that the Contribution is Your original
work, or that You have all rights necessary to grant the rights set
out in §§4.1–4.2, and that submission of the Contribution does not
violate any third-party right.

If You are not willing to grant the rights in §§4.1–4.2, please do not
submit Contributions. We may, at our discretion, require a Contributor
Licence Agreement (CLA) be signed before accepting substantial
Contributions.

---

## 5. Conditions

5.1 **Attribution.** Any copy of the Software, in source or object
form, must include this licence and the copyright notice in §0
unmodified.

5.2 **No removal of patent notices.** The Patent is cited inline at
every relevant code site (search the source for `[US10827161B2 col.
N]`). You must not remove, obscure, or alter these citations.

5.3 **No trademark licence.** This licence does not grant any right to
use the names, logos, or trademarks of Djinn Technologies Ltd. or
Akuma Engineering Ltd., except for the limited purpose of describing
the origin of the Software.

5.4 **Third-party components.** The Software may include code from
third parties under their own licences (for example,
`codec/tests/vectors/ryg_rans_*.h` is public-domain reference code
from Fabian Giesen, used at test-build time only). Such third-party
licences continue to govern the third-party code; this licence
governs the Software *as a whole* and the original code authored by
Djinn.

---

## 6. Termination

6.1 **Automatic termination on breach.** Your rights under this
licence terminate automatically and immediately if You materially
breach any of its terms, including (without limitation) by engaging in
Commercial Use without a separate written licence from Djinn or by
asserting any patent right against Djinn or any of Djinn's licensees
of the Software in respect of the Software.

6.2 **Effect of termination.** Upon termination, You must immediately
cease all use of the Software and destroy all copies of the Software
in Your possession or control. Sections 3 (Patent Reservation), 4
(Contributions, in respect of any Contribution submitted before
termination), 7 (Disclaimer), 8 (Limitation of Liability), and 9
(Governing Law and Dispute Resolution) survive termination.

6.3 **Reinstatement.** If a breach is curable and is cured within
thirty (30) days of Djinn's written notice, this licence may be
reinstated at Djinn's sole discretion.

---

## 7. Disclaimer of Warranty

THE SOFTWARE IS PROVIDED **"AS IS"**, WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, NON-INFRINGEMENT,
AND TITLE. WITHOUT LIMITING THE FOREGOING, DJINN MAKES NO
REPRESENTATION OR WARRANTY THAT THE SOFTWARE WILL OPERATE WITHOUT
INTERRUPTION OR ERROR, OR THAT IT WILL MEET YOUR REQUIREMENTS, OR
THAT IT IS FREE FROM HARMFUL CODE.

You acknowledge that the Software is a research-grade reference
implementation and should be independently validated before use in any
context where errors could give rise to harm.

---

## 8. Limitation of Liability

TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, IN NO EVENT WILL
DJINN BE LIABLE TO YOU FOR ANY INDIRECT, INCIDENTAL, SPECIAL,
CONSEQUENTIAL, PUNITIVE, OR EXEMPLARY DAMAGES, INCLUDING BUT NOT
LIMITED TO LOSS OF PROFITS, REVENUE, DATA, OR USE, ARISING OUT OF OR
RELATED TO THIS LICENCE OR THE SOFTWARE, EVEN IF DJINN HAS BEEN
ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.

DJINN'S AGGREGATE LIABILITY UNDER THIS LICENCE WILL NOT EXCEED
**ONE HUNDRED THOUSAND POUNDS STERLING (£100,000)**.

Some jurisdictions do not allow the exclusion or limitation of certain
damages; in such jurisdictions, the limitations above apply only to
the extent permitted.

---

## 9. Governing Law and Dispute Resolution

This licence is governed by the laws of **England and Wales**, without
regard to its conflict-of-laws provisions.

The courts of **England and Wales** have exclusive jurisdiction to
settle any dispute arising out of or in connection with this licence,
save that Djinn may bring proceedings in any jurisdiction where
infringement of the Patent or breach of this licence is taking place.

The United Nations Convention on Contracts for the International Sale
of Goods does not apply.

---

## 10. Commercial Licensing and Contact

For any of the following, contact Djinn Technologies Ltd.:

- **Commercial licence enquiries** (production deployment, distribution,
  hosted-service / SaaS use, OEM bundling, hardware integration);
- **Patent licence enquiries** (US 10,827,161 B2 and equivalents);
- **Custom evaluation periods** beyond the sixty-day default;
- **Trademark and branding enquiries**; or
- **Questions about the scope of this licence** before You begin a
  particular use.

> **Contact:**
> Djinn Technologies Ltd.
> 111 New Union Street
> Coventry, West Midlands
> CV1 2NT, England
> Company No.: 13918535 (registered in England and Wales)
> Email: licensing@djinn.cloud
> Web:   https://djinn.cloud

---

## 11. Miscellaneous

11.1 **Entire agreement.** This licence is the entire agreement
between You and Djinn concerning the Software and supersedes any
prior or contemporaneous communications.

11.2 **No waiver.** Djinn's failure to enforce any provision of this
licence does not waive that provision or any other.

11.3 **Severability.** If any provision is held unenforceable, the
remaining provisions continue in full force and effect.

11.4 **No assignment.** You may not assign or transfer this licence
without Djinn's prior written consent. Djinn may assign this licence
freely, including to an affiliate or successor in interest.

11.5 **Headings.** Headings are for convenience only and do not affect
the interpretation of this licence.

---

*Spatial Compression Codec — Source-Available Reference License v1.0,
last revised 2026-05-07.*
