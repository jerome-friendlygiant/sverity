// Byte shifter used by a simple SPI-style master.
module shift_reg8 (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       load,
  input  logic       shift_en,
  input  logic [7:0] par_in,
  input  logic       ser_in,
  output logic       ser_out,
  output logic [7:0] par_out
);
  logic [7:0] q;
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)        q <= 8'h00;
    else if (load)     q <= par_in;
    else if (shift_en) q <= {q[6:0], ser_in};
  end
  assign par_out = q;
  assign ser_out = q[7];
endmodule

module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       start,
  input  logic [7:0] tx_byte,
  input  logic       miso,
  output logic       mosi,
  output logic [7:0] rx_byte,
  output logic       busy
);
  logic [3:0] bit_cnt;
  logic       load_pulse;

  assign busy       = (bit_cnt != 4'd0);
  assign load_pulse = start & ~busy;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)          bit_cnt <= 4'd0;
    else if (load_pulse) bit_cnt <= 4'd8;
    else if (busy)       bit_cnt <= bit_cnt - 4'd1;
  end

  shift_reg8 u_sr (
    .clk(clk),
    .rst_n(rst_n),
    .load(),
    .shift_en(busy),
    .par_in(tx_byte),
    .ser_in(miso),
    .ser_out(mosi),
    .par_out(rx_byte)
  );
endmodule
