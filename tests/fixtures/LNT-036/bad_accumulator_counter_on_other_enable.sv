// sverity-args: --top top
// cnt advances on its own enable, not with the accumulation: acc can grow while cnt stands still.
// expect: LNT-036 WARNING 15
// expect-count: LNT-036 WARNING 1
module top(input logic clk, rst_n, vld, input logic tick, input logic [7:0] s, output logic [7:0] mean_o);
  logic [15:0] acc;
  logic [3:0] cnt;
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      acc <= '0;
      cnt <= '0;
      mean_o <= '0;
    end else if (vld) begin
      if (cnt == 4'd15) begin
        mean_o <= 8'((acc + 16'(s)) >> 4);
        acc <= '0;
        cnt <= '0;
      end else begin
        acc <= acc + 16'(s);
      end
      if (tick) cnt <= cnt + 1'b1;
    end
  end
endmodule
