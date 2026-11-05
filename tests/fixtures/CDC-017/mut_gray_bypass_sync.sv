// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// MUTATION of good_gray_pointer.sv: the second synchroniser stage is bypassed, the pointer is captured by one flop.
// expect: CDC-017 ERROR
// CDC-004 is evidence of the primary CDC-017 finding
module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  logic [3:0] bin, gray;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) gray <= '0; else gray <= bin ^ (bin >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= '0; else q <= gray;
endmodule
