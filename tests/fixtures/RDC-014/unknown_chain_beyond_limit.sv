// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A 17-stage chain exceeds the recognition limit: it is neither a recognised synchroniser nor a data register, so the reset's driver is Unresolved (pulse width unproved) and the scope INCOMPLETE.
// expect-unresolved: RDC-014
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, rn, d, output logic q);
  logic [16:0] s;
  always_ff @(posedge clka or negedge rn) if (!rn) s <= '0; else s <= {s[15:0], 1'b1};
  always_ff @(posedge clka or negedge s[16]) if (!s[16]) q <= 1'b0; else q <= d;
endmodule
