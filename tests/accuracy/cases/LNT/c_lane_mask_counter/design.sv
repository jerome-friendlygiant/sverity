// Byte-lane accounting for a 32-bit stream: counts the valid lanes in each
// beat and keeps a running byte total until the last beat of a frame.
typedef struct packed {
  logic [31:0] data;
  logic [3:0]  strb;
  logic        last;
} beat_t;

module top (
  input  logic                   clk,
  input  logic                   rst_n,
  input  logic                   in_valid,
  input  beat_t                  in_beat,
  output logic [2:0]             lanes_o,
  output logic [15:0]            total_bytes,
  output logic [$bits(beat_t)-1:0] raw_o
);
  localparam int LANES = 4;
  localparam int CW    = $clog2(LANES + 1);

  function automatic logic [CW-1:0] count_lanes(input logic [LANES-1:0] m);
    logic [CW-1:0] c;
    c = '0;
    for (int i = 0; i < LANES; i++) c = c + CW'(m[i]);
    return c;
  endfunction

  logic [CW-1:0] lanes_c;
  logic [$bits(beat_t)-1:0] raw_q;

  assign lanes_c = count_lanes(in_beat.strb);
  assign lanes_o = lanes_c;
  assign raw_o   = raw_q;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      total_bytes <= 16'd0;
      raw_q       <= '0;
    end else if (in_valid) begin
      raw_q       <= in_beat;
      total_bytes <= in_beat.last ? 16'd0 : total_bytes + 16'(lanes_c);
    end
  end
endmodule
