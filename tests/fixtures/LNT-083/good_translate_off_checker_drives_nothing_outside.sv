// sverity-args: --top top
// The region holds a checker only: its signals are declared and used inside it, and it prints. Nothing the design reads comes from it,
// so the region and the findings inside it are advisory.
// expect: LNT-083 INFO 10
// expect-none: LNT-083 WARNING
// expect-none: LNT-061 WARNING
// expect-none: LNT-040 WARNING
module top(input logic clk, input logic a, output logic y);
  always_ff @(posedge clk) y <= a;
  // synopsys translate_off
  logic prev;
  always @(posedge clk) begin
    prev <= y;
    if (prev !== y) $display("changed");
  end
  // synopsys translate_on
endmodule
