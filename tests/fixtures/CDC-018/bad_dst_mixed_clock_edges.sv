// sverity-args: --top top --sdc @DIR@/skew.sdc
// Destination Gray siblings captured on opposite clock edges violate coherent sampling.
// expect: CDC-018 WARNING
// expect-unresolved: CDC-018
// expect-none: CDC-018 INFO
module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  logic [3:0] bin, gray, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) gray <= '0; else gray <= bin ^ (bin >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) begin s1[1:0] <= '0; s2[1:0] <= '0; end else begin s1[1:0] <= gray[1:0]; s2[1:0] <= s1[1:0]; end
  always_ff @(negedge clkb or negedge rst_n) if (!rst_n) begin s1[3:2] <= '0; s2[3:2] <= '0; end else begin s1[3:2] <= gray[3:2]; s2[3:2] <= s1[3:2]; end
  assign q = s2;
endmodule
