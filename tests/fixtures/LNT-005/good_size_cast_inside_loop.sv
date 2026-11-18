// sverity-args: --top top
// Plain P2: 3'(i) of a loop variable that runs 0..7 inside the body. Every value fits the 3-bit cast, so the unsigned
// result equals i. The sign-conversion finding is INFO with the proof.
// expect: LNT-005 INFO 9
// expect-none: LNT-005 WARNING
module top(input logic [7:0] d, output logic [2:0] last);
  always_comb begin
    last = '0;
    for (int i = 0; i < 8; i++) if (d[i]) last = 3'(i);
  end
endmodule
