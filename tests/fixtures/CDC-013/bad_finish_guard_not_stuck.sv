// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A terminating system call ($finish) between writes prevents proving src stuck at 0.
// expect: CDC-013 WARNING
// expect: CDC-014 WARNING
module top(input logic clka, clkb, rst_a, output logic q);
  logic src, tog_q, s1, s2;
  always @(posedge clka) begin
    src <= 1'b1;
    $finish;
    src <= 1'b0;
  end
  always_ff @(posedge clka or posedge rst_a) begin
    if (rst_a) tog_q <= 1'b0;
    else tog_q <= tog_q ^ src;
  end
  always_ff @(posedge clkb or posedge rst_a) begin
    if (rst_a) {s2, s1} <= '0;
    else {s2, s1} <= {s1, tog_q};
  end
  assign q = s2;
endmodule
