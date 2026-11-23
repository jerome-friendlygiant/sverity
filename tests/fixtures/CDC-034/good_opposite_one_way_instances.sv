// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Opposite-direction one-way pointers in separate instances are not a FIFO pair.
// expect-none: CDC-034
// expect-none: CDC-024 INFO
module write_pointer(input logic clka, clkb, rst_n, inc);
  logic [2:0] bin, gray, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) gray <= '0; else gray <= bin ^ (bin >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, gray};
endmodule
module read_pointer(input logic clka, clkb, rst_n, inc);
  logic [2:0] bin, gray, s1, s2;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 1;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) gray <= '0; else gray <= bin ^ (bin >> 1);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, gray};
endmodule
module top(input logic clka, clkb, rst_n, inc);
  write_pointer w(clka, clkb, rst_n, inc);
  read_pointer r(clka, clkb, rst_n, inc);
endmodule
