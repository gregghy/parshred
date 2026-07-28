# Parshred Commercial License

Parshred is dual-licensed. You may use it under either:

1. **GNU Affero General Public License v3.0 or later (AGPL-3.0-or-later)** —
   see [`LICENSE`](LICENSE) and
   <https://www.gnu.org/licenses/agpl-3.0.html>. Free for open-source use.
2. **Parshred Commercial License** — for proprietary / closed-source / SaaS
   use that the AGPL does not permit. Requires a signed commercial agreement
   and payment of the applicable license fee.

This file describes when the commercial license is required, what it grants,
and how to obtain it. The actual grant is made by a signed commercial
agreement, not by this document alone.

---

## When You Need a Commercial License

You need a commercial license if **any** of the following is true:

- You use Parshred in **proprietary software** that you do not release under
  AGPL-3.0-or-later (or a compatible copyleft license) to all recipients.
- You use Parshred in a **network service, SaaS product, API, or web
  application** whose source code you do not make available to users of that
  service under AGPL-3.0-or-later. (AGPL §13 extends copyleft to remote
  network interaction.)
- You **modify** Parshred and distribute or serve the modified version
  without sharing the modifications under AGPL-3.0-or-later.
- You **embed or bundle** Parshred in a product distributed to customers
  under a license that is not AGPL-3.0-or-later compatible.
- Your **legal, compliance, or procurement** team requires a conventional
  commercial license to approve use, even if AGPL might technically permit
  your use case.

You **do not** need a commercial license if:

- Your project is open-source under AGPL-3.0-or-later (or a compatible
  license such as GPL-3.0-or-later) and you comply with its terms.
- You use Parshred for **personal, academic, or research** use that does not
  trigger AGPL distribution or network-interaction obligations.
- You are **evaluating** Parshred for potential commercial use. Evaluation
  and internal prototyping are permitted under the AGPL version.

When in doubt, contact us before deploying. Unauthorized commercial use
without a valid commercial license is a copyright violation.

---

## What the Commercial License Grants

A signed commercial license agreement grants you, for the license term:

- **Permission to use** Parshred in proprietary, closed-source, and SaaS
  products without any copyleft obligation to disclose your source code.
- **Permission to modify** Parshred internally without sharing
  modifications.
- **Permission to distribute** Parshred (modified or unmodified) as part of
  your product under your own license terms.
- **No per-seat, per-CPU, or per-deployment limits** — the license covers
  your entire organization for the contracted use case.
- **Priority support**: bug fixes, security patches, and feature requests
  handled ahead of the public queue, with agreed response SLAs.
- **Indemnification options** where offered in the signed agreement.
- **Warranty** to the extent stated in the signed agreement. (The AGPL
  version is provided "as is" with no warranty.)

The commercial license does **not** grant you ownership of Parshred or the
right to relicense Parshred itself to third parties as a standalone product.

---

## Pricing

Pricing is set per signed agreement and depends on company size, use case,
and support tier. Indicative tiers:

| Tier        | Company size        | Typical use                                   |
|-------------|---------------------|-----------------------------------------------|
| Startup     | < 50 employees      | Single product, community support             |
| Business    | 50–500 employees    | Multiple products, priority support           |
| Enterprise  | 500+ employees      | Site license, SLA, indemnification, escrow    |

Custom terms (perpetual vs. subscription, multi-year, escrow, on-prem vs.
SaaS) are available. Contact us for a quote.

---

## How to Obtain a Commercial License

1. Email **licensing@parshred.dev** with:
   - Your company name and country of incorporation.
   - Intended use case (product type, SaaS vs. on-prem, distribution model).
   - Approximate number of users / deployments.
   - Required support tier and SLA expectations.
2. We will send a quote and a draft commercial license agreement.
3. Both parties sign the agreement; we issue an invoice.
4. Upon payment, you receive a signed license certificate and access to the
   priority support channel.

A 30-day evaluation license is available for commercial evaluation. Contact
us to request one.

---

## FAQ

**Q: Can I try Parshred before buying a commercial license?**
A: Yes. The AGPL version is fully functional. You may evaluate it, prototype
with it, and use it in production for open-source projects indefinitely. For
commercial evaluation that goes beyond what AGPL permits, request a 30-day
evaluation license.

**Q: Does the AGPL apply if I only use Parshred internally, with no external
users?**
A: AGPL's copyleft triggers on "conveying" (distribution) and "remote
network interaction" (users interacting with the software over a network).
Purely internal use with no external users may not trigger AGPL obligations,
but the boundary is fact-specific. If there is any doubt — for example,
contractors or affiliates accessing the service — a commercial license
removes the ambiguity.

**Q: I contribute a patch to Parshred. Do I need a commercial license to use
my own patch?**
A: Contributions are governed by our Contributor License Agreement (see
[`docs/cla/CLA-INDIVIDUAL.md`](docs/cla/CLA-INDIVIDUAL.md)). The CLA grants
us the right to offer your contribution under both the AGPL and commercial
licenses. You retain copyright. You may use your own contribution under the
AGPL without a commercial license; using it in a proprietary product still
requires a commercial license for Parshred as a whole.

**Q: Can I get a trial commercial license?**
A: Yes. We offer 30-day evaluation licenses for commercial use. Contact
licensing@parshred.dev.

**Q: What happens if my commercial license expires?**
A: You must either renew, switch to AGPL-compliant use, or cease using
Parshred in non-AGPL-compliant ways. Code you wrote that uses Parshred
remains yours; only the Parshred grant lapses.

**Q: Do you offer escrow?**
A: Yes, source-code escrow is available on Enterprise tier agreements.

---

## Contact

- **Email:** licensing@parshred.dev
- **Repository:** <https://github.com/parshred/parshred>
- **CLA:** [`docs/cla/`](docs/cla/)
