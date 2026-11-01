// sverity-args: --top top
// expect-none: LNT-038
module top(input logic clk, output logic hit, hit2);
  logic [7:0] cnt;
  always_ff @(posedge clk) cnt <= cnt + 1;
  assign hit = (cnt == 255);        // reachable
  assign hit2 = (cnt >= 200);
endmodule
