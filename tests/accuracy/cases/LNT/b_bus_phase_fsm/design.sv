// Four-phase bus master sequencer.
typedef enum logic [1:0] {
  PH_IDLE = 2'd0,
  PH_ADDR = 2'd1,
  PH_DATA = 2'd2,
  PH_RESP = 2'd3
} phase_t;

module top (
  input  logic clk,
  input  logic rst_n,
  input  logic req,
  input  logic addr_ok,
  input  logic data_ok,
  input  logic resp_ack,
  output logic addr_en,
  output logic data_en,
  output logic resp_wait
);
  phase_t phase, phase_n;

  always_comb begin
    phase_n = phase;
    unique case (phase)
      PH_IDLE: if (req)      phase_n = PH_ADDR;
      PH_ADDR: if (addr_ok)  phase_n = PH_DATA;
      PH_DATA: if (data_ok)  phase_n = PH_RESP;
      PH_RESP: if (resp_ack) phase_n = PH_IDLE;
    endcase
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) phase <= PH_IDLE;
    else        phase <= phase_n;
  end

  assign addr_en   = (phase == PH_ADDR);
  assign data_en   = (phase == PH_DATA);
  assign resp_wait = (phase == PH_RESP);
endmodule
