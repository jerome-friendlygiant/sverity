// sverity-args: --top top --sdc @DIR@/skew.sdc
// Gray conversion shape and cross-bit feedback do not establish a unit-step counter.
// expect: CDC-018 WARNING
// expect-unresolved: CDC-018
// expect-none: CDC-017
module top(input logic clka, clkb, rst_n, output logic [3:0] q);
  logic [3:0] lfsr, gray, s1, s2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) lfsr <= 4'b0001;
    else lfsr <= {lfsr[2:0], lfsr[3] ^ lfsr[2]};
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) gray <= '0;
    else gray <= lfsr ^ (lfsr >> 1);
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {s2, s1} <= '0;
    else {s2, s1} <= {s1, gray};
  assign q = s2;
endmodule
