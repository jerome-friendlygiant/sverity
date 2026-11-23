// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A destination enable around same-domain data is ordinary clock-enable behavior, not CDC-028.
// expect-none: CDC-028
module top(input logic clka, clkb, en, d, output logic q);
  logic source;
  always_ff @(posedge clka) source <= d;
  always_ff @(posedge clka) if (en) q <= source;
endmodule
