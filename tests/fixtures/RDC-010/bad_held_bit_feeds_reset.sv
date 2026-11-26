// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Bit 1 of v is held during rn, not reset, and feeds the reset register q: after reset q can capture a stale bit.
// revert: if (!rn) v[0] <= 1'b0; else v <= {d, e}; => if (!rn) v <= 2'b0; else v <= {d, e};
// revert-expect-none: RDC-010 WARNING
// expect: RDC-010 WARNING
module top(input logic clka, rn, d, e, output logic q);
  logic [1:0] v;
  always_ff @(posedge clka or negedge rn) if (!rn) v[0] <= 1'b0; else v <= {d, e};
  always_ff @(posedge clka or negedge rn) if (!rn) q <= 1'b0; else q <= v[1];
endmodule
