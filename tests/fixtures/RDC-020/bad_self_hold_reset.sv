// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Bit 1 is assigned its own value in the reset branch: it holds during reset and is not reset.
// revert: v[1] <= v[1]; => v[1] <= 1'b0;
// revert-expect-none: RDC-020 WARNING
// expect: RDC-020 WARNING
module top(input logic clka, rn, d, e, output logic q);
  logic [1:0] v;
  always_ff @(posedge clka or negedge rn) if (!rn) begin v[0] <= 1'b0; v[1] <= v[1]; end else v <= {d, e};
  assign q = &v;
endmodule
