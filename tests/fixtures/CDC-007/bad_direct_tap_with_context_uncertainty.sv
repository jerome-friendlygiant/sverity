// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-007 ERROR
// expect-unresolved: CDC-003
module top(input logic clka, clkb, d, output logic q, tap);
  import "DPI-C" context function void opaque_effect();
  logic source, first, second;
  always_ff @(posedge clka) begin
    source <= d;
    opaque_effect();
  end
  always_ff @(posedge clkb) begin
    first <= source;
    second <= first;
    tap <= ~first;
  end
  assign q = second;
endmodule
