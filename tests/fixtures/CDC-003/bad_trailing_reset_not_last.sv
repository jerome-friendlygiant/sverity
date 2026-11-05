// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// CDC-003 is evidence of the primary CDC-028 finding
// expect: CDC-028 ERROR
module top(input logic clka, clkb, rst_b, d, other, output logic q);
  logic src, a, b;
  always_ff @(posedge clka) src <= d;
  always_ff @(posedge clkb) begin
    a <= src; b <= a;
    if (rst_b) begin a <= 0; b <= 0; end
    a <= other;
  end
  assign q = b;
endmodule
