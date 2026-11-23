// sverity-args: --top top --sdc @DIR@/skew.sdc
// Active-low async reset written as rst_n == 1'b0 is equivalent to !rst_n.
// expect: CDC-018 INFO
// expect-none: CDC-018 WARNING
module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  logic [3:0] bin, gray, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (rst_n == 1'b0) bin <= '0; else if (inc) bin <= bin + 1;
  always_ff @(posedge clka or negedge rst_n) if (rst_n == 1'b0) gray <= '0; else gray <= bin ^ (bin >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (rst_n == 1'b0) {s2, s1} <= '0; else {s2, s1} <= {s1, gray};
  assign q = s2;
endmodule
