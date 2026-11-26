// sverity-args: --top top
// Bad lookalike: the for loop only resets 3 of 4 array elements, leaving element 3 unreset -> RDC-020 partial reset WARNING.
// revert: for (int i = 0; i < 3; i++) => for (int i = 0; i < 4; i++)
// revert-expect-none: RDC-020
// expect: RDC-020 WARNING
// expect-confidence: RDC-020 heuristic
module top(input logic clk, rst_n, we, input logic [1:0] wa, input logic [3:0] wd, output logic [3:0] y);
  logic [3:0] bank [4];
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin
      for (int i = 0; i < 3; i++) bank[i] <= '0;
    end else if (we) begin
      bank[wa] <= wd;
    end
  assign y = bank[3];
endmodule
