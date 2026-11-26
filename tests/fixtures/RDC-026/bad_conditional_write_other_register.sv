// sverity-args: --top top
// The reset clears q and the set sets q, both unconditionally: a real set/clear conflict on q (Established, line 8).
// A conditional write to spare sits in its own block (line 10): spare alone is Unresolved, and q is not downgraded.
// expect-confidence: RDC-026 established 8
// expect-confidence: RDC-026 unresolved 10
module top(input logic clka, rst_n, set_n, en, d, output logic q);
  logic spare;
  always_ff @(posedge clka or negedge rst_n or negedge set_n)
    if (!rst_n) q <= 0; else if (!set_n) q <= 1; else q <= d;
  always_ff @(posedge clka or negedge rst_n or negedge set_n)
    if (!rst_n) begin if (en) spare <= 0; end else if (!set_n) spare <= 0; else spare <= d;
endmodule
