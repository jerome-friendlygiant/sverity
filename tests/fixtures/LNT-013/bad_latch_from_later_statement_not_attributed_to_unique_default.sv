// sverity-args: --top top
// The unique case assigns `o` on every path; `p` is written only by the `if` after it. That `if` is the one latch, reported once at the
// `if`; the empty-looking default item of the case is not the cause and is not named.
// expect: LNT-013 WARNING 12
// expect-count: LNT-013 WARNING 1
module top(input logic [1:0] s, input logic g, input logic [3:0] d, output logic [3:0] o, p);
  always_comb begin
    unique case (s)
      2'd0: o = d;
      default: o = '0;
    endcase
    if (g) p = d;
  end
endmodule
