// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-unresolved: CDC-003
// expect-none: CDC-003 ERROR
// expect-coverage: graph-precision INCOMPLETE
module top(input logic clka, clkb, d, output logic q);
  import "DPI-C" context function void opaque_effect();
  logic source_q, first_stage, second_stage;
  always_ff @(posedge clka) source_q <= d;
  always_ff @(posedge clkb) begin
    first_stage <= source_q;
    second_stage <= first_stage;
    opaque_effect();
  end
  assign q = second_stage;
endmodule
