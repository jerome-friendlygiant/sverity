// sverity-args: --top top
// A recursive task that writes m1 stops expanding at the bound: one finding at the call site, naming m1 and not m2.
// expect: LNT-049 WARNING 14
// expect-count: LNT-049 WARNING 1
module top(input logic clk, input int n, input logic [3:0] d, output logic [3:0] y);
  logic [3:0] m1 [0:3];
  logic [3:0] m2 [0:3];
  task automatic fill(input int k);
    if (k > 0) begin
      m1[k[1:0]] <= d;
      fill(k - 1);
    end
  endtask
  always_ff @(posedge clk) fill(n);
  assign y = m1[0] ^ m2[0];
endmodule
