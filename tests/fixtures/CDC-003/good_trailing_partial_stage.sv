// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-003 INFO
// expect-none: CDC-003 ERROR
module top(input logic clka, clkb, rst_b, d, output logic q);
  logic src, a, b;
  always_ff @(posedge clka) src <= d;
  always_ff @(posedge clkb) begin
    a <= src; b <= a;
    if (rst_b) a <= 0;
  end
  assign q = b;
endmodule
