// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An output (no reset word in its name) selects between two resets with a data register: two reset leaves make it an exported reset.
// The select is a register output, so the glitch behaviour is a property of that register: Heuristic, not Unresolved.
// expect: RDC-009 WARNING
// expect-confidence: RDC-009 heuristic
module top(input logic clka, rsta_n, rstb_n, d, output logic q, o);
  logic sel_q;
  always_ff @(posedge clka or negedge rsta_n) if (!rsta_n) sel_q <= 1'b0; else sel_q <= d;
  always_ff @(posedge clka or negedge rstb_n) if (!rstb_n) q <= 1'b0; else q <= d;
  assign o = sel_q ? rsta_n : rstb_n;
endmodule
