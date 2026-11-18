// CRC-8 (poly 0x07) accumulator, one byte per enabled cycle.
module crc8_step (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       clear,
  input  logic       en,
  input  logic [7:0] din,
  output logic [7:0] crc
);
  function automatic logic [7:0] next_crc(input logic [7:0] c, input logic [7:0] d);
    logic [7:0] x;
    x = c ^ d;
    for (int i = 0; i < 8; i++)
      x = x[7] ? ((x << 1) ^ 8'h07) : (x << 1);
    return x;
  endfunction

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)     crc <= 8'h00;
    else if (clear) crc <= 8'h00;
    else if (en)    crc <= next_crc(crc, din);
  end
endmodule

// Separate checksums for frame header and payload.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       frame_start,
  input  logic       hdr_valid,
  input  logic       pay_valid,
  input  logic [7:0] hdr_byte,
  input  logic [7:0] pay_byte,
  output logic [7:0] hdr_crc,
  output logic [7:0] pay_crc
);
  crc8_step u_hdr (
    .clk(clk), .rst_n(rst_n), .clear(frame_start),
    .en(hdr_valid), .din(hdr_byte), .crc(hdr_crc)
  );
  crc8_step u_pay (
    .clk(clk), .rst_n(rst_n),
    .en(pay_valid), .din(pay_byte), .crc(pay_crc)
  );
endmodule
