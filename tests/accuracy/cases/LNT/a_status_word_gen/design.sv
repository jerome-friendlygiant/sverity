// Status word: four live flags in the low nibble, reserved bits read as zero.
// A flag is set by its set_flags bit and cleared by its clr_flags bit;
// a simultaneous clear wins.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [3:0] set_flags,
  input  logic [3:0] clr_flags,
  output logic [7:0] status,
  output logic       any_flag
);
  logic [3:0] flags_q;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) flags_q <= 4'h0;
    else        flags_q <= (flags_q | set_flags) & ~clr_flags;
  end

  for (genvar i = 0; i < 8; i++) begin : g_bit
    if (i < 4) begin : g_live
      assign status[i] = flags_q[i];
    end else begin : g_rsvd
      assign status[i] = 1'b0;
    end
  end

  assign any_flag = |flags_q;
endmodule
