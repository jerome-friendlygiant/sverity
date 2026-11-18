// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       link_en_i,
  output logic [7:0] frame_byte_o,
  output logic       frame_byte_valid_o,
  output logic [3:0] byte_cnt_o
);
  logic [7:0] rx_data;
  logic       rx_valid;
  logic [7:0] byte_q;
  logic       valid_q;
  logic [3:0] cnt_q;

  serdes_rx_macro u_phy (
    .clk      (clk_i),
    .rst_n    (rst_ni),
    .enable   (link_en_i),
    .rx_data  (rx_data),
    .rx_valid (rx_valid)
  );

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      byte_q  <= '0;
      valid_q <= 1'b0;
      cnt_q   <= '0;
    end else begin
      valid_q <= rx_valid;
      if (rx_valid) begin
        byte_q <= rx_data;
        cnt_q  <= cnt_q + 4'd1;
      end
    end
  end

  assign frame_byte_o       = byte_q;
  assign frame_byte_valid_o = valid_q;
  assign byte_cnt_o         = cnt_q;
endmodule
