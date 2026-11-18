// Occupancy tracker for a 16-entry buffer with push and pop strobes.
// Flags: empty, half full and an overflow indication.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       push,
  input  logic       pop,
  output logic       empty,
  output logic       half,
  output logic       overflow
);
  logic [3:0] fill_cnt;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      fill_cnt <= 4'd0;
    end else begin
      case ({push, pop})
        2'b10:   fill_cnt <= fill_cnt + 4'd1;
        2'b01:   fill_cnt <= (fill_cnt == 4'd0) ? fill_cnt : fill_cnt - 4'd1;
        default: fill_cnt <= fill_cnt;
      endcase
    end
  end

  assign empty    = (fill_cnt == 4'd0);
  assign half     = (fill_cnt >= 4'd8);
  assign overflow = (fill_cnt > 4'd15);
endmodule
