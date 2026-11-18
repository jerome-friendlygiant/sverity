`ifdef WIDE_SAMPLES
  `define SAMPLE_W 12
`else
  `define SAMPLE_W 8
`endif
// Registered adapter between the sample source and an 8-bit processing stage.
// The source width follows the platform option WIDE_SAMPLES.
// dout_valid follows din_valid one cycle later; flush drops a beat in flight.
module top (
  input  logic                   clk,
  input  logic                   rst_n,
  input  logic                   flush,
  input  logic                   din_valid,
  input  logic [`SAMPLE_W-1:0]   din,
  output logic                   dout_valid,
  output logic [7:0]             dout
);
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      dout_valid <= 1'b0;
      dout       <= '0;
    end else begin
      dout_valid <= din_valid && !flush;
      if (din_valid) dout <= din;
    end
  end
endmodule
