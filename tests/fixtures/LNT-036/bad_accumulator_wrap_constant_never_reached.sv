// sverity-args: --top top
// The wrap constant 8 does not fit the 3-bit counter, so the clear never happens and acc is unbounded.
// expect: LNT-036 WARNING 15
// expect-count: LNT-036 WARNING 1
module top(input logic clk, rst_n, vld, input logic [7:0] s, output logic [7:0] mean_o);
  logic [15:0] acc;
  logic [2:0] cnt;
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      acc <= '0;
      cnt <= '0;
      mean_o <= '0;
    end else if (vld) begin
      if (cnt == 4'd8) begin
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
