// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: vendor_ip u(.clk(clka), .d(d), .q(o)); => assign o = d;
// revert-expect-none: CDC-023 WARNING
// expect: CDC-023 WARNING
module top(input logic clka, clkb, d, output logic q);
  logic o;
  vendor_ip u(.clk(clka), .d(d), .q(o));
  always_ff @(posedge clkb) q <= o;
endmodule
