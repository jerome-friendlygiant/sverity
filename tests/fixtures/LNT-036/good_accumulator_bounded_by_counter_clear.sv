// sverity-args: --top top
// acc is set only with cnt, grows by at most 255 per step while cnt < 15: acc <= 3825, so the sum keeps its carry in 16 bits.
// expect-none: LNT-036
module top(input logic clk, rst_n, vld, input logic [7:0] s, output logic [7:0] mean_o);
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
        cnt <= cnt + 1'b1;
      end
    end
  end
endmodule
