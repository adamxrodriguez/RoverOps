import { useRef } from 'react';

export function HowToUse() {
  const dialog = useRef<HTMLDialogElement>(null);

  return (
    <>
      <button
        type="button"
        className="help-link"
        aria-haspopup="dialog"
        aria-controls="rover-guide"
        onClick={() => dialog.current?.showModal()}
      >
        How to use <span aria-hidden="true">↗</span>
      </button>
      <dialog
        ref={dialog}
        id="rover-guide"
        className="guide-dialog"
        aria-labelledby="guide-title"
        aria-describedby="guide-intro"
        onClick={(event) => {
          if (event.target !== event.currentTarget) return;
          const bounds = event.currentTarget.getBoundingClientRect();
          if (
            event.clientX < bounds.left ||
            event.clientX > bounds.right ||
            event.clientY < bounds.top ||
            event.clientY > bounds.bottom
          )
            dialog.current?.close();
        }}
      >
        <div className="guide-content">
          <div className="guide-heading">
            <div>
              <p className="eyebrow">QUICK START</p>
              <h2 id="guide-title">Your first rover mission</h2>
            </div>
            <button
              type="button"
              className="guide-close"
              aria-label="Close guide"
              autoFocus
              onClick={() => dialog.current?.close()}
            >
              <span aria-hidden="true">×</span>
            </button>
          </div>
          <p id="guide-intro" className="guide-intro">
            You control a virtual robot. The teal arrow is your rover; the gold circle is its
            destination. No physical robot is connected.
          </p>
          <ol className="guide-steps">
            <li>
              <h3>Choose a destination</h3>
              <p>
                Wait for <strong>Live telemetry</strong>. Try <strong>X = 8</strong> and{' '}
                <strong>Y = 5</strong>, then click <strong>Set waypoint</strong>. That’s 8 meters
                right and 5 meters up from home.
              </p>
            </li>
            <li>
              <h3>Start and watch</h3>
              <p>
                Click <strong>Start</strong>. The rover turns and drives toward the circle. The
                numbers show its speed, direction, distance, and simulated battery.
              </p>
            </li>
            <li>
              <h3>Try the controls</h3>
              <p>
                <strong>Set velocity</strong> changes its speed. <strong>Pause</strong> holds it
                still; <strong>Resume</strong> continues. <strong>Return home</strong> sends it back
                to the starting point.
              </p>
            </li>
          </ol>
          <div className="guide-stop">
            <strong>Emergency stop</strong>
            <p>
              Stops the rover and holds it until you click <strong>Resume</strong>. If it was
              already paused, click Resume again to continue.
            </p>
          </div>
          <p className="guide-note">
            In the public demo, everyone controls the same rover. Opening this guide does not pause
            a mission.
          </p>
          <form method="dialog" className="guide-actions">
            <button className="primary">Got it</button>
          </form>
        </div>
      </dialog>
    </>
  );
}
