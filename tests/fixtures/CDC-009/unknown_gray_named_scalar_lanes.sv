// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-009 WARNING
// expect-unresolved: CDC-009
// expect-coverage: cdc-017 INCOMPLETE
module sync2(input logic clk, d, output logic q);
  logic s1;
  always_ff @(posedge clk) begin
    s1 <= d;
    q <= s1;
  end
endmodule
module top(input logic clka, clkb, inc, output logic [3:0] q);
  logic [3:0] bin, gray, bin_next;
  assign bin_next = bin + {3'b0, inc};
  always_ff @(posedge clka) begin
    bin <= bin_next;
    gray <= bin_next ^ (bin_next >> 1);
  end
  sync2 u0(.clk(clkb), .d(gray[0]), .q(q[0]));
  sync2 u1(.clk(clkb), .d(gray[1]), .q(q[1]));
  sync2 u2(.clk(clkb), .d(gray[2]), .q(q[2]));
  sync2 u3(.clk(clkb), .d(gray[3]), .q(q[3]));
endmodule
