// sverity-args: --top top
// slang also rejects the second driver (LNT-009); the mixed-style finding is independent of that
// expect: LNT-063 WARNING 5
module top(input logic clk, a, output logic s);
  always @(posedge clk) s <= a;
  always @(posedge clk) s = ~a;
endmodule
