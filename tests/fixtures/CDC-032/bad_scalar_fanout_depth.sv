// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-032 ERROR
module top(input logic clka, clkb, d, en, output logic x, y, z);
  logic src, a, b, c, d1, e, f;
  always_ff @(posedge clka) src <= d;
  always_ff @(posedge clkb) begin
    a <= src; b <= a;
    c <= src; d1 <= c;
    e <= src; f <= e ^ en;
  end
  assign x = b; assign y = d1; assign z = f;
endmodule
