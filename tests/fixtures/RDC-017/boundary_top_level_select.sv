// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Edge of the trigger: the gate select of the logic-built reset r is a primary input, not a register, and top-level
// selects are not checked by RDC-017 (RDC-009 still reports the gated reset). Making sel a register would fire.
// expect-none: RDC-017
// expect: RDC-009 WARNING
module top(input logic clka, rst_n, sel, d, output logic q);
  logic r;
  assign r = rst_n | sel;
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
