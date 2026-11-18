// Transmit byte mux for a framer: bytes matching the programmed idle pattern
// are replaced by the pad byte.
//   idle_pat  pattern register written by the host
//   pad_byte  replacement byte
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [7:0] data_in,
  input  logic [7:0] idle_pat,
  input  logic [7:0] pad_byte,
  output logic [7:0] tx_byte,
  output logic       padded
);
  logic is_idle;

  assign is_idle = (data_in ==? idle_pat);

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      tx_byte <= 8'h00;
      padded  <= 1'b0;
    end else begin
      tx_byte <= is_idle ? pad_byte : data_in;
      padded  <= is_idle;
    end
  end
endmodule
