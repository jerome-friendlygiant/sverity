// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-028 ERROR
// expect-none: CDC-003 INFO
module top(input logic clka, clkb, rstb, d, output logic o);
  logic source, first, second;
  wire inverted_clear = ~rstb;
  always_ff @(posedge clka) source <= d;
  always_ff @(posedge clkb) begin
    first <= source;
    second <= first;
    if (rstb) first <= 0;
    if (inverted_clear) first <= 0;
  end
  assign o = second;
endmodule
