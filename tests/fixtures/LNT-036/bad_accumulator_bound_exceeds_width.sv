// sverity-args: --top top
// Same accumulate-and-clear, but 15 samples of 8 bits need 12 bits and acc has 10: the carry is lost before the shift.
// expect: LNT-036 WARNING 15
// expect-count: LNT-036 WARNING 1
module top(input logic clk, rst_n, vld, input logic [7:0] s, output logic [7:0] mean_o);
  logic [9:0] acc;
  logic [3:0] cnt;
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      acc <= '0;
      cnt <= '0;
      mean_o <= '0;
    end else if (vld) begin
      if (cnt == 4'd15) begin
        mean_o <= 8'((acc + 10'(s)) >> 4);
        acc <= '0;
        cnt <= '0;
      end else begin
        acc <= acc + 10'(s);
        cnt <= cnt + 1'b1;
      end
    end
  end
endmodule
