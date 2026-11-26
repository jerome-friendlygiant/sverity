// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Output q is bit 1 of v, which rn holds rather than resets: the output depends on an unreset bit.
// revert: if (!rn) v[0] <= 1'b0; else v <= {d, e}; => if (!rn) v <= 2'b0; else v <= {d, e};
// revert-expect-none: RDC-025 WARNING
// expect: RDC-025 WARNING
module top(input logic clka, rn, d, e, output logic q);
  logic [1:0] v;
  always_ff @(posedge clka or negedge rn) if (!rn) v[0] <= 1'b0; else v <= {d, e};
  assign q = v[1];
endmodule
