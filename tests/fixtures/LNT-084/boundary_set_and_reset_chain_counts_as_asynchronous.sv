// sverity-args: --top top
// Both controls of a set-and-reset flop are asynchronous resets: `set_n` tested synchronously elsewhere is the finding.
// expect: LNT-084 WARNING 9
module top(input logic clk, rst_n, set_n, d, output logic q1, q2);
  always_ff @(posedge clk or negedge rst_n or negedge set_n)
    if (!rst_n) q1 <= 1'b0;
    else if (!set_n) q1 <= 1'b1;
    else q1 <= d;
  always_ff @(posedge clk)
    if (!set_n) q2 <= 1'b1;
    else q2 <= d;
endmodule
