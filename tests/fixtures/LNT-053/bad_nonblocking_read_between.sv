// sverity-args: --top top
// a read of t between two nonblocking updates sees the OLD value of t, so the first update is still overwritten unread
// expect: LNT-053 WARNING 6
module top(input logic clk, a, b, output logic y, t);
  always_ff @(posedge clk) begin
    t <= a;
    y <= t;
    t <= b;
  end
endmodule
