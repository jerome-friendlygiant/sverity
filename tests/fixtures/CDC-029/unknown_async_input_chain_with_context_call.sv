// sverity-args: --top top --sdc @FIXTURES@/CDC-006/input_async.sdc
// expect-unresolved: CDC-029
// expect-none: CDC-029 INFO
// expect-coverage: graph-precision INCOMPLETE
module top(input logic clka, clkb, d, output logic q);
  import "DPI-C" context function void opaque_effect();
  logic first;
  always_ff @(posedge clkb) begin
    first <= d;
    q <= first;
    opaque_effect();
  end
endmodule
