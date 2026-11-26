// sverity-args: --top top
// Local loop counters in while/repeat reset loops inside the async reset branch are not functional uses of the reset (no false RDC-019).
// expect-none: RDC-019
// expect-none: CST-010
// expect: RDC-025 WARNING
// expect-unresolved: RDC-025
module top(input logic clk, rst_n, input logic [1:0] a, input logic [3:0] d, output logic [3:0] y1, y2);
  logic [3:0] m1 [4];
  logic [3:0] m2 [4];
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin
      int i;
      i = 0;
      while (i < 4) begin m1[i] <= '0; i++; end
    end else m1[a] <= d;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin
      int j;
      j = 0;
      repeat (4) begin m2[j] <= '0; j++; end
    end else m2[a] <= d;
  assign y1 = m1[3];
  assign y2 = m2[3];
endmodule
