// Per-channel gain registers (five channels) with a read-back mux.
// ch_sel comes from the channel sequencer.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       cfg_wr,
  input  logic [2:0] cfg_ch,
  input  logic [7:0] cfg_data,
  input  logic [2:0] ch_sel,
  output logic [7:0] gain_o
);
  localparam int N_CH = 5;

  logic [7:0] gain_q [N_CH];

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      for (int i = 0; i < N_CH; i++) gain_q[i] <= 8'h10;
    end else if (cfg_wr) begin
      for (int i = 0; i < N_CH; i++) begin
        if (cfg_ch == 3'(i)) gain_q[i] <= cfg_data;
      end
    end
  end

  assign gain_o = gain_q[ch_sel];
endmodule
