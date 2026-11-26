// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The reset branch assigns only bit 0; bit 1 keeps its value while rn is asserted, so it is not reset.
// revert: if (!rn) v[0] <= 1'b0; else v <= {d, e}; => if (!rn) v <= 2'b0; else v <= {d, e};
// revert-expect-none: RDC-020 WARNING
// expect: RDC-020 WARNING
module top(input logic clka, rn, d, e, output logic q);
  logic [1:0] v;
  always_ff @(posedge clka or negedge rn) if (!rn) v[0] <= 1'b0; else v <= {d, e};
  assign q = &v;
endmodule
