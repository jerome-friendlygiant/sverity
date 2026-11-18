// sverity-args: --top top
// A combinational case that builds a record (it decodes a command field of the same enum type as a state register elsewhere, and writes
// that field in its default arm) assigns no state: there is no transition relation to attribute.
// expect-none: LNT-029
// expect-none: LNT-071
module top(input logic clk, input logic rst_n, input logic go, input logic [3:0] d, output logic [5:0] out, output logic busy);
  typedef enum logic [1:0] {IDLE, RUN, DONE, INV} cmd_e;
  typedef struct packed { cmd_e cmd; logic [3:0] data; } rec_t;
  rec_t req, core;
  cmd_e st_q;
  assign req = {RUN, d};
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) st_q <= IDLE;
    else if (go) st_q <= cmd_e'(d[1:0]);
  assign busy = st_q == RUN;
  always_comb begin
    core = req;
    case (req.cmd)
      IDLE: core.data = 4'd0;
      RUN: core.data = req.data ^ 4'd1;
      default: core.cmd = INV;
    endcase
  end
  assign out = core;
endmodule
