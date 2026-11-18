// sverity-args: --top top
// Two instance outputs drive words 1 and 3 of the array, and a clocked process writes word 2: no word has two writers.
// expect: LNT-049 INFO 9
// expect-none: LNT-049 WARNING
module leaf(input logic [3:0] d, output logic [3:0] q);
  assign q = ~d;
endmodule
module top(input logic clk, we, input logic [3:0] d, output logic [3:0] y);
  logic [3:0] m [4];
  leaf u_a (.d(d), .q(m[1]));
  leaf u_b (.d(~d), .q(m[3]));
  always_ff @(posedge clk) if (we) m[2] <= d;
  assign y = m[1] ^ m[2] ^ m[3];
endmodule
