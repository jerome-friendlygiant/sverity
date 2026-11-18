// Transmit-only UART: baud divider plus a 10-bit frame shifter.
// A frame is one start bit, eight data bits (LSB first) and one stop bit.
module top #(
  parameter int DIV = 16
) (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       send,
  input  logic [7:0] data,
  output logic       txd,
  output logic       busy
);
  localparam int CW = $clog2(DIV);

  logic [CW-1:0] baud_cnt;
  logic          baud_tick;
  logic [9:0]    shreg;
  logic [3:0]    bits_left;

  assign baud_tick = (baud_cnt == CW'(DIV - 1));

  // Divider restarts at every bit boundary and while idle.
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)                       baud_cnt <= '0;
    else if (busy && !baud_tick)      baud_cnt <= baud_cnt + 1'b1;
    else                              baud_cnt <= '0;
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      shreg     <= 10'h3ff;
      bits_left <= 4'd0;
    end else if (!busy && send) begin
      shreg     <= {1'b1, data, 1'b0};
      bits_left <= 4'd10;
    end else if (busy && baud_tick) begin
      shreg     <= {1'b1, shreg[9:1]};
      bits_left <= bits_left - 4'd1;
    end
  end

  assign busy = (bits_left != 4'd0);
  assign #2 txd = shreg[0];
endmodule
