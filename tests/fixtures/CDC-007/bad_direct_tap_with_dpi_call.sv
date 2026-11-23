// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-007 ERROR
// expect-none: CDC-003 WARNING
module top(input logic clka, clkb, d, output logic q, tap);
  import "DPI-C" function bit transform(input bit x);
  logic source, first, second;
  always_ff @(posedge clka) source <= d;
  always_ff @(posedge clkb) begin
    first <= source;
    second <= first;
    tap <= first & transform(first);
  end
  assign q = second;
endmodule
